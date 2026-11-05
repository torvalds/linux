.. SPDX-License-Identifier: GPL-2.0

A interface de drivers do kernel Linux
=======================================

(todas as suas perguntas respondidas e mais algumas)

Greg Kroah-Hartman <greg@kroah.com>

Este texto foi escrito para tentar explicar por que o Linux **não possui uma
interface binária do kernel nem uma interface estável do kernel**.

.. note::

   Observe que este artigo descreve as interfaces **internas do kernel**, e não
   as interfaces entre o kernel e o espaço de usuário.

   A interface entre o kernel e o espaço de usuário é aquela utilizada pelos
   aplicativos: a interface de chamadas de sistema (syscalls). Essa
   interface é **muito** estável ao longo do tempo e não será quebrada. Tenho
   programas antigos, compilados em uma versão do kernel anterior à 0.9 e alguma
   coisa, que ainda funcionam perfeitamente na versão mais recente do kernel
   2.6. Essa é a interface cuja estabilidade os usuários e desenvolvedores de
   aplicativos podem considerar garantida.


Resumo executivo
----------------

Você acha que quer uma interface estável do kernel, mas, na verdade, não quer,
e nem sabe disso. O que você realmente quer é um driver que continue
funcionando de maneira estável, e isso só é possível se o seu driver estiver
na árvore principal do kernel. Você também obtém muitos outros benefícios se
o seu driver fizer parte da árvore principal do kernel. São esses benefícios
que ajudaram a tornar o Linux um sistema operacional tão robusto, estável e
maduro — justamente a razão pela qual você o está usando.


Introdução
----------

Apenas quem escreve drivers para o kernel precisa se preocupar com as mudanças
nas interfaces internas do kernel. Para a grande maioria das pessoas, essas
interfaces nem sequer são visíveis e tampouco são motivo de preocupação.

Antes de mais nada, não abordarei **nenhuma** questão jurídica relacionada a
código-fonte fechado, código-fonte oculto, blobs binários, wrappers de
código-fonte ou qualquer outro termo usado para descrever drivers do kernel
cujo código-fonte não seja disponibilizado sob a GPL. Consulte um advogado
caso tenha alguma dúvida jurídica. Sou programador e, portanto, descreverei
aqui apenas as questões técnicas (isso não significa que as questões
jurídicas sejam pouco importantes; elas são reais e você precisa estar sempre
ciente delas).

Portanto, há dois tópicos principais: interfaces binárias do kernel e
interfaces estáveis de código-fonte do kernel. Ambos dependem um do outro,
mas discutiremos primeiro a parte referente às interfaces binárias para
deixá-la de lado.


Interface binária do kernel
---------------------------

Supondo que tivéssemos uma interface estável de código-fonte para o kernel,
uma interface binária surgiria naturalmente também, certo? Errado. Considere
os seguintes fatos sobre o kernel Linux:

  - Dependendo da versão do compilador C utilizada, diferentes estruturas de
    dados do kernel terão diferentes alinhamentos e poderão até mesmo incluir
    funções de maneiras distintas (por exemplo, tornando determinadas funções
    inline ou não). A organização das funções individuais não é tão importante,
    mas as diferenças no preenchimento das estruturas de dados são muito
    importantes.
  - Dependendo das opções selecionadas durante a compilação do kernel, uma
    grande variedade de comportamentos pode ser assumida pelo kernel:

      - diferentes estruturas podem conter campos diferentes;
      - algumas funções podem nem sequer ser implementadas (por exemplo,
        determinados bloqueios são completamente eliminados durante a
        compilação em kernels sem SMP);
      - a memória dentro do kernel pode ser alinhada de maneiras diferentes,
        dependendo das opções de compilação.
  - O Linux é executado em uma grande variedade de arquiteturas de
    processadores. Não há como drivers binários compilados para uma arquitetura
    funcionarem corretamente em outra.

Vários desses problemas podem ser contornados simplesmente compilando o módulo
para uma configuração específica e exata do kernel, utilizando exatamente o
mesmo compilador C empregado na compilação do kernel. Isso é suficiente caso
você queira fornecer um módulo para uma determinada versão de uma distribuição
Linux específica. Porém, multiplique essa única compilação pelo número de
distribuições Linux existentes e pelo número de versões suportadas de cada
distribuição e você rapidamente terá um pesadelo de diferentes opções de
compilação em diferentes versões. Além disso, cada versão de uma distribuição
Linux contém vários kernels, cada um ajustado para diferentes tipos de
hardware (diferentes tipos de processadores e diferentes opções). Portanto,
mesmo para uma única versão, você precisará criar várias versões do seu módulo.

Acredite em mim: com o tempo, você enlouquecerá se tentar oferecer suporte a
esse tipo de distribuição. Aprendi isso da maneira mais difícil há muito
tempo...

Interfaces estáveis de código-fonte do kernel
----------------------------------------------

Esse é um tópico um pouco mais "volátil" se você conversar com alguém que está
tentando manter atualizado, ao longo do tempo, um driver do kernel Linux que
não está na árvore principal do kernel.

O desenvolvimento do kernel Linux é contínuo e ocorre em ritmo acelerado,
sem desacelerar. Por isso, os desenvolvedores do kernel encontram bugs nas
interfaces existentes ou descobrem maneiras melhores de fazer as coisas.
Quando isso acontece, eles corrigem as interfaces atuais para que funcionem
melhor. Nesse processo, nomes de funções podem mudar, estruturas podem crescer
ou diminuir e parâmetros de funções podem ser reformulados. Quando isso
acontece, todos os locais dentro do kernel que utilizam essa interface são
corrigidos ao mesmo tempo, garantindo que tudo continue funcionando
corretamente.

Como exemplos específicos disso, as interfaces USB internas do kernel
passaram por pelo menos três reformulações diferentes ao longo da existência
desse subsistema. Essas reformulações foram feitas para resolver diversos
problemas:

  - Uma mudança de um modelo síncrono de fluxos de dados para um modelo
    assíncrono. Isso reduziu a complexidade de vários drivers e aumentou a
    taxa de transferência de todos os drivers USB, de modo que atualmente
    executamos quase todos os dispositivos USB na maior velocidade possível.
  - Foi feita uma mudança na maneira como os pacotes de dados eram alocados
    pelos drivers USB a partir do núcleo USB, de modo que todos os drivers
    passaram a precisar fornecer mais informações ao núcleo USB, corrigindo
    diversos deadlocks documentados.

Isso contrasta fortemente com vários sistemas operacionais de código fechado,
que tiveram de manter suas interfaces USB antigas ao longo do tempo. Isso
permite que novos desenvolvedores utilizem acidentalmente interfaces antigas
e façam as coisas de maneira inadequada, prejudicando a estabilidade do
sistema operacional.

Em ambos os casos, todos os desenvolvedores concordaram que essas eram
mudanças importantes que precisavam ser feitas, e elas foram realizadas com
relativamente pouco esforço. Se o Linux tivesse de garantir a preservação de
uma interface de código-fonte estável, uma nova interface teria de ser criada,
enquanto a interface antiga e defeituosa teria de continuar sendo mantida ao
longo do tempo, resultando em trabalho adicional para os desenvolvedores USB.
Como todos os desenvolvedores USB do Linux realizam esse trabalho em seu
próprio tempo, pedir que programadores façam trabalho extra, sem nenhum
benefício e gratuitamente, não é uma possibilidade.

Questões de segurança também são muito importantes para o Linux. Quando um
problema de segurança é encontrado, ele é corrigido em um período muito curto.
Em diversas ocasiões, isso fez com que interfaces internas do kernel fossem
reformuladas para impedir que o problema de segurança ocorresse. Quando isso
acontece, todos os drivers que utilizam essas interfaces também são corrigidos
ao mesmo tempo, garantindo que o problema de segurança seja resolvido e não
possa reaparecer acidentalmente no futuro. Se as interfaces internas não
pudessem ser alteradas, não seria possível corrigir esse tipo de problema de
segurança e garantir que ele não voltasse a ocorrer.

As interfaces do kernel são aprimoradas ao longo do tempo. Se ninguém estiver
utilizando uma determinada interface, ela é removida. Isso garante que o
kernel permaneça o menor possível e que todas as interfaces existentes possam
ser testadas da melhor maneira possível (é praticamente impossível testar
adequadamente a validade de interfaces que não são utilizadas).


O que fazer
-----------

Então, se você possui um driver do kernel Linux que não está na árvore
principal do kernel, o que você, como desenvolvedor, deve fazer? Distribuir
um driver binário para cada versão diferente do kernel em cada distribuição
é um pesadelo, e tentar acompanhar uma interface do kernel que está em
constante mudança também é uma tarefa difícil.

Simples: coloque seu driver na árvore principal do kernel (lembre-se de que
estamos falando aqui de drivers distribuídos sob uma licença compatível com
a GPL; se seu código não se enquadra nessa categoria, boa sorte, você está
por conta própria aqui, seu parasita). Se seu driver estiver na árvore e uma
interface do kernel mudar, ele será corrigido pela própria pessoa que realizou
a alteração no kernel. Isso garante que seu driver continue sempre compilável
e funcionando ao longo do tempo, exigindo muito pouco esforço de sua parte.

Os excelentes efeitos colaterais de ter seu driver na árvore principal do
kernel são:

  - A qualidade do driver aumentará, enquanto os custos de manutenção
    (para o desenvolvedor original) diminuirão.
  - Outros desenvolvedores adicionarão funcionalidades ao seu driver.
  - Outras pessoas encontrarão e corrigirão bugs no seu driver.
  - Outras pessoas encontrarão oportunidades de otimização no seu driver.
  - Outras pessoas atualizarão o driver para você quando mudanças em
    interfaces externas exigirem isso.
  - O driver será automaticamente distribuído por todas as distribuições
    Linux, sem que seja necessário pedir às distribuições que o adicionem.

Como o Linux oferece suporte, "pronto para uso", a um número maior de
dispositivos diferentes do que qualquer outro sistema operacional, e oferece
suporte a esses dispositivos em mais arquiteturas de processadores diferentes
do que qualquer outro sistema operacional, esse modelo comprovado de
desenvolvimento deve estar fazendo alguma coisa certa :)


------

Agradecimentos a Randy Dunlap, Andrew Morton, David Brownell, Hanna Linder,
Robert Love e Nishanth Aravamudan pela revisão e pelos comentários sobre
este documento.
