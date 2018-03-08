node {
	stage('plugin') {
		timestamps {
			checkout scm
		}
	}
	stage('cleanup') {
		timestamps {
			sh "rm -rf *"
		}
	}
}