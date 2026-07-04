.. SPDX-License-Identifier: GPL-2.0

Estilo de gerenciamento do kernel Linux
=======================================

Este é um documento curto descrevendo o estilo de gerenciamento preferido (ou
inventado, dependendo de quem você perguntar) para o kernel do Linux. Ele se
destina a espelhar o documento :ref:`process/coding-style.rst <codingstyle>` em
algum grau, e foi escrito principalmente para evitar responder [#f1]_ as mesmas
(ou semelhantes) perguntas repetidamente.

Estilo de gerenciamento é muito pessoal e muito mais difícil de quantificar do
que simples regras de estilo de codificação, então este documento pode ou não ter
qualquer coisa a ver com a realidade. Começou como uma brincadeira, mas isso não
significa que não possa ser verdade. Você terá que decidir por si mesmo.

A propósito, quando falamos sobre "gerente do kernel", trata-se de pessoas líderes
técnicas, e não das pessoas que fazem gerenciamento tradicional dentro das empresas.
Se você assina pedidos de compra ou tem alguma ideia sobre o orçamento do seu grupo,
você quase certamente não é um gerente do kernel. Essas sugestões podem ou não se
aplicar a você.

Primeiro, eu sugeriria comprar "Os Sete Hábitos das Pessoas Altamente Eficazes"
e NÃO lê-lo. Queime-o, é um ótimo gesto simbólico.

.. [#f1] Este documento faz isso não respondendo tanto à pergunta, mas torna
  dolorosamente óbvio para o questionador que não temos ideia de qual é a resposta.

De qualquer maneira, aqui vai:

.. _decisoes:

1) Decisões
-----------

Todo mundo pensa que os gerentes tomam decisões, e que a tomada de decisões é
importante. Quanto maior e mais dolorosa a decisão, maior deve ser o gerente para
tomá-la. Isso é muito profundo e óbvio, mas na verdade não é verdade.

O nome do jogo é **evitar** ter que tomar uma decisão. Em particular, se alguém
lhe disser "escolha (a) ou (b), realmente precisamos que você decida sobre isso",
você está em apuros como gerente. As pessoas que você gerencia devem conhecer os
detalhes melhor do que você, então se elas vierem até você para uma decisão técnica,
você está ferrado. Você claramente não é competente para tomar essa decisão por elas.

(Consequência: Se as pessoas que você gerencia não conhecem os detalhes melhor
do que você, você também está ferrado, embora por um motivo totalmente diferente.
Isso significa que você está no trabalho errado, e que **elas** deveriam estar
gerenciando sua genialidade em vez disso).

Então o nome do jogo é **evitar** decisões, pelo menos as grandes e dolorosas.
Tomar decisões pequenas e sem consequências é bom, e faz você parecer que sabe
o que está fazendo, então o que um gerente do kernel precisa fazer é transformar
as grandes e dolorosas em pequenas coisas com as quais ninguém realmente se importa.

Ajuda a perceber que a diferença fundamental entre uma grande decisão e uma pequena
é se você pode consertar sua decisão depois. Qualquer decisão pode ser pequena
garantindo sempre que, se você estiver errado (e você **vai** estar errado), você
sempre pode desfazer o dano mais tarde voltando atrás. De repente, você demonstra
o dobro de capacidade como gerente por tomar **duas** decisões inconsequentes
- a errada **e** a certa.

E as pessoas verão isso como verdadeira liderança (*cof cof* besteira *cof cof*).

Portanto a chave para evitar grandes decisões torna-se apenas evitar fazer coisas
que não podem ser desfeitas. Não se deixe encurralar em um canto do qual você não
possa escapar. Um rato encurralado pode ser perigoso - um gerente encurralado é
apenas lamentável.

Como ninguém seria estúpido o suficiente para realmente deixar um gerente do kernel
ter uma enorme responsabilidade fiscal **de qualquer maneira**, é geralmente
bastante fácil voltar atrás. Como você não vai ser capaz de desperdiçar enormes
quantidades de dinheiro que você pode não ser capaz de reembolsar, a única coisa que
você pode voltar atrás é uma decisão técnica, e lá o retrocesso é muito fácil:
apenas diga a todos que você era um imbecil incompetente, peça desculpas e desfaça
todo o trabalho inútil em que você fez as pessoas trabalharem no último ano.
De repente, a decisão que você tomou há um ano não era uma grande decisão afinal,
já que poderia ser facilmente desfeita.

Acontece que algumas pessoas têm problemas com essa abordagem, por dois motivos:

 - admitir que você foi um idiota é mais difícil do que parece. Todos nós gostamos
   de manter as aparências, e sair em público para dizer que você estava errado
   às vezes é muito difícil mesmo.
 - ter alguém falando que o que você trabalhou no último ano não valeu a pena
   depois de tudo pode ser difícil para os pobres engenheiros humildes também,
   e enquanto o **trabalho** real foi fácil de desfazer apenas excluindo-o, você
   pode ter perdido irrevogavelmente a confiança desse engenheiro. E lembre-se:
   "irrevogável" era o que tentamos evitar em primeiro lugar, e sua decisão acabou
   sendo uma grande decisão afinal.

Felizmente, ambas essas razões podem ser mitigadas efetivamente apenas admitindo
de antemão que você não tem a menor ideia, e dizendo às pessoas antes do fato
que sua decisão é puramente preliminar, e pode ser a coisa errada. Você deve
sempre reservar o direito de mudar de ideia, e fazer com que as pessoas estejam
muito **cientes** disso. E é muito mais fácil admitir que você é estúpido quando
você ainda não fez a coisa realmente estúpida.

Então, quando realmente se revela estúpido, as pessoas apenas reviram os olhos
e dizem "Ops, não de novo".

Essa admissão preventiva de incompetência também pode fazer com que as pessoas
que realmente fazem o trabalho também pensem duas vezes sobre se vale a pena ou
não. Afinal, se **elas** não têm certeza se é uma boa ideia, você com certeza não
deve encorajá-las prometendo que o que elas trabalham será incluído. Faça com que
elas pelo menos pensem duas vezes antes de embarcar em um grande empreendimento.

Lembre-se: eles devem saber mais sobre os detalhes do que você, e geralmente já
pensam que têm a resposta para tudo. A melhor coisa que você pode fazer como
gerente é não incutir confiança, mas sim uma dose saudável de pensamento crítico
sobre o que eles fazem.

A propósito, um outro jeito de evitar uma decisão é simplesmente choramingar "não
podemos fazer os dois?" e parecer patético. Confie em mim, funciona. Se não estiver
claro qual abordagem é melhor, eles eventualmente descobrirão. A resposta pode
acabar sendo que ambas as equipes ficam tão frustradas com a situação que apenas
desistem.

Isso pode soar como uma falha, mas geralmente é um sinal de que havia algo errado
com ambos os projetos, e a razão pela qual as pessoas envolvidas não conseguiram
decidir foi que ambas estavam erradas. Você acaba saindo por cima, e evitou mais
uma decisão que poderia ter estragado.

2) Pessoas
----------

A maioria das pessoas é idiota, e ser um gerente significa que você terá que lidar
com isso, e talvez mais importante, que **elas** terão que lidar com **você**.

Aconteceu que enquanto é fácil desfazer erros técnicos, não é tão fácil desfazer
distúrbios de personalidade. Você só tem que conviver com os deles - e com os seus.

Entretanto, para se preparar como gerente do kernel, é melhor lembrar de não queimar
nenhuma ponte, bombardear nenhum vilarejo inocente ou alienar muitos desenvolvedores
do kernel. Acontece que alienar pessoas é bastante fácil, e reverter esse afastamento
é difícil. Assim, "alienar" cai imediatamente sob o título de "não reversível",
e se torna um não-não de acordo com :ref:`decisoes`.

Existem apenas algumas regras simples aqui:

 (1) não chame as pessoas de imbecis (pelo menos não em público)
 (2) aprenda a pedir desculpas quando você esquecer a regra (1)

O problema com #1 é que é muito fácil de fazer, já que você pode dizer "você é
um imbecil" de milhões de maneiras diferentes [#f2]_, às vezes sem nem perceber,
e quase sempre com uma convicção ardente de que você está certo.

E quanto mais convencido você estiver de que está certo (e vamos encarar, você
pode chamar praticamente qualquer pessoa de imbecil, e muitas vezes você **vai**
estar certo), mais difícil acaba sendo se desculpar depois.

Para resolver esse problema, você realmente só tem duas opções:

 - fique muito bom em pedir desculpas
 - espalhe o "amor" de forma tão uniforme que ninguém realmente acabe se sentindo
   injustamente alvo. Torne-o inventivo o suficiente, e eles podem até se divertir.

A opção de ser infalivelmente educado realmente não existe. Ninguém confiará em
alguém que está claramente escondendo seu verdadeiro caráter.

.. [#f2] Paul Simon cantou "Fifty Ways to Leave Your Lover", porque francamente,
  "A Million Ways to Tell a Developer They're a D*ckhead" não soa tão bem. Mas
  tenho certeza de que ele pensou sobre isso.


3) Pessoas II - o tipo bom
--------------------------

Embora no final das contas a maioria das pessoas seja idiota, a consequência disso
é tristemente que você também é, e que enquanto todos nós podemos nos deleitar na
segura convicção de que somos melhores do que a pessoa média (vamos encarar, ninguém
nunca acredita que é mediano ou abaixo da média), também devemos admitir que não
somos a faca mais afiada por aí, e haverá outras pessoas que são menos idiotas
do que você.

Algumas pessoas reagem mal a pessoas inteligentes. Outras se aproveitam delas.

Tenha certeza de que você, como mantenedor do kernel, está no segundo grupo.
Puxe o saco delas, porque são as pessoas que tornarão seu trabalho mais fácil.
Em particular, elas serão capazes de tomar suas decisões por você, que é tudo
sobre o jogo.

Então quando você encontrar alguém mais inteligente do que você, apenas siga o
fluxo. Suas responsabilidades de gerenciamento tornam-se em grande parte dizer
"Parece uma boa ideia - pode ir fundo", ou "Isso parece bom, mas e quanto a xxx?".
A segunda versão, em particular, é uma ótima maneira de aprender algo novo sobre
"xxx" ou parecer **extra** gerencial ao apontar algo que a pessoa mais inteligente
não havia pensado. Em qualquer caso, você vence.

Uma coisa a se observar é perceber que a grandeza em uma área não se traduz
necessariamente em outras áreas. Então você pode instigar as pessoas em direções
específicas, mas vamos encarar, elas podem ser boas no que fazem e péssimas em
tudo o mais. A boa notícia é que as pessoas tendem a naturalmente voltar para o
que são boas, então não é como se você estivesse fazendo algo irreversível quando
você **as** instiga em alguma direção, apenas não pressione demais.

4) Colocando a culpa
--------------------

As coisas vão dar errado, e as pessoas querem alguém para culpar. Pronto, a culpa
é sua.

Não é realmente tão difícil aceitar a culpa, especialmente se as pessoas perceberem
que não foi **toda** a sua culpa. O que nos leva à melhor maneira de assumir a
culpa: faça isso por outra pessoa. Você se sentirá bem por assumir a culpa, eles
se sentirão bem por não serem culpados, e a pessoa que perdeu toda a coleção de
pornografia de 36 GB por causa da sua incompetência vai admitir relutantemente que
pelo menos você não tentou se esquivar disso.

Então faça o desenvolvedor que realmente estragou (se você conseguir encontrá-lo)
saber **em particular** que ele estragou. Não apenas para que ele possa evitar isso
no futuro, mas para que ele saiba que lhe deve uma. E, talvez ainda mais importante,
ele provavelmente é a pessoa que pode consertar. Porque, vamos encarar, com certeza
não é você.

Levar a culpa também é o motivo pelo qual você se torna gerente em primeiro lugar.
É parte do que faz as pessoas confiarem em você, e permite a você a glória potencial,
porque você é quem pode dizer "Eu estraguei". E se você seguiu as regras anteriores,
você será muito bom em dizer isso agora.

5) Coisas para evitar
---------------------

Tem uma coisa que as pessoas odeiam ainda mais do que ser chamado de "idiota", e
isso é ser chamado de "idiota" com uma voz moralista. O primeiro você pode se
desculpar, o segundo você realmente não terá a chance. Eles provavelmente não
estarão mais ouvindo, mesmo que você faça um bom trabalho de outra forma.

Todos nós pensamos que somos melhores do que qualquer outra pessoa, o que significa
que quando alguém posa de superior, isso realmente nos irrita. Você pode ser moral
e intelectualmente superior a todos ao seu redor, mas não tente tornar isso muito
óbvio, a menos que você realmente **pretenda** irritar alguém [#f3]_.

Semelhantemente, não seja muito educado ou sutil sobre as coisas. A educação
facilmente acaba indo longe demais e escondendo o problema, e como dizem, "Na
internet, ninguém pode ouvir você sendo sutil". Use um grande objeto contundente
para martelar o ponto, porque você realmente não pode depender das pessoas
entenderem o seu ponto de outra forma.

Um pouco de humor pode ajudar a amortecer tanto a franqueza quanto a moralização.
Ir além do limite a ponto de ser ridículo pode transmitir um ponto sem tornar
doloroso para o destinatário, que apenas pensa que você está sendo bobo. Isso pode
ajudar a superar o bloqueio mental pessoal que todos nós temos sobre críticas.

.. [#f3] Dica: grupos de discussão na internet que não estão diretamente relacionados
  ao seu trabalho são ótimas maneiras de descarregar suas frustrações nos outros.
  Escreva posts insultuosos com sarcasmo apenas para entrar em uma boa discussão
  de vez em quando, e você se sentirá aliviado. Só não faça sujeira muito perto
  de casa (ou seja, não crie problemas onde isso possa afetar sua vida pessoal).

6) Por que eu?
--------------

Já que a sua maior responsabilidade parece ser assumir a culpa pelos erros de outras
pessoas, e tornar dolorosamente óbvio para todos os outros que você é incompetente,
a pergunta óbvia se torna: por que fazer isso em primeiro lugar?

Primeiramente, embora você possa ou não receber adolescentes gritando (meninas ou
meninos, não vamos ser preconceituosos ou sexistas aqui) batendo na porta do seu
camarim, você **vai** receber uma imensa sensação de realização pessoal por estar
"no comando". Não importa o fato de que você realmente está liderando tentando
acompanhar todos os outros e correndo atrás deles o mais rápido que puder. Todos
ainda vão pensar que você é a pessoa no comando.

É um ótimo trabalho se você conseguir aguentar.
